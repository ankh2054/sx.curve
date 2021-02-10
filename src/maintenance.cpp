[[eosio::action]]
void sx::curve::test( const uint64_t amount, const uint64_t reserve_in, const uint64_t reserve_out, const uint64_t amplifier, const uint64_t fee )
{
    print ("\ncurrent get_amount_out(amount: ",amount,", amp: ",amplifier, "  ): ", Curve::get_amount_out( amount, reserve_in, reserve_out, amplifier, fee ));
    // print ("\nnew get_amount_out(amount: ",amount,", amp: ",amplifier * 100,"): ", Curve::get_amount_out_bips( amount, reserve_in, reserve_out, amplifier * 100, fee ));

    check(false, "see print");
}

[[eosio::action]]
void sx::curve::reset( const name table )
{
    require_auth( get_self() );

    sx::curve::config_table _config( get_self(), get_self().value );
    sx::curve::pairs_table _pairs( get_self(), get_self().value );
    sx::curve::backup_table _backup( get_self(), get_self().value );
    sx::curve::orders_table _orders( get_self(), get_self().value );
    sx::curve::ramp_table _ramp( get_self(), get_self().value );

    if ( table == "config"_n ) _config.remove();
    else if ( table == "pairs"_n ) clear_table( _pairs );
    else if ( table == "orders"_n ) clear_table( _orders );
    else if ( table == "backup"_n ) clear_table( _backup );
    else if ( table == "ramp"_n ) clear_table( _ramp );
    else check( false, "reset not available");
}

template <typename T>
void sx::curve::clear_table( T& table )
{
    auto itr = table.begin();
    while ( itr != table.end() ) {
        itr = table.erase( itr );
    }
}

[[eosio::action]]
void sx::curve::update( const symbol_code pair_id )
{
    require_auth( get_self() );

    sx::curve::pairs_table _pairs( get_self(), get_self().value );

    auto itr = _pairs.find( pair_id.raw() );
    // _pairs.modify( itr, get_self(), [&]( auto & row ) {
    //     row.liquidity.contract = TOKEN_CONTRACT;
    //     row.liquidity.quantity = liquidity;
    // });
    create( itr->liquidity.get_extended_symbol() );
    issue( itr->liquidity, "issue" );
}

[[eosio::action]]
void sx::curve::backup()
{
    require_auth( get_self() );

    sx::curve::pairs_table _pairs( get_self(), get_self().value );
    sx::curve::backup_table _backup( get_self(), get_self().value );
    sx::curve::config_table _config( get_self(), get_self().value );
    clear_table( _backup );

    auto itr = _pairs.begin();
    check( itr != _pairs.end(), "pairs is empty");
    while ( itr != _pairs.end() ) {
        _backup.emplace( get_self(), [&]( auto & row ) {
            row.id = itr->id;
            row.reserve0 = itr->reserve0;
            row.reserve1 = itr->reserve1;
            row.liquidity = itr->liquidity;
            row.amplifier = itr->amplifier;
            row.virtual_price = itr->virtual_price;
            row.price0_last = itr->price0_last;
            row.price1_last = itr->price1_last;
            row.volume0 = itr->volume0;
            row.volume1 = itr->volume1;
            row.trades = itr->trades;
            row.last_updated = itr->last_updated;
        });
        ++itr;
    }
    sx::curve::setstatus_action setstatus( get_self(), { get_self(), "active"_n });
    setstatus.send( "backup"_n );
}

[[eosio::action]]
void sx::curve::copy()
{
    require_auth( get_self() );

    sx::curve::pairs_table _pairs( get_self(), get_self().value );
    sx::curve::backup_table _backup( get_self(), get_self().value );
    clear_table( _pairs );

    auto itr = _backup.begin();
    check( itr != _backup.end(), "backup is empty");
    while ( itr != _backup.end() ) {
        _pairs.emplace( get_self(), [&]( auto & row ) {
            row.id = itr->id;
            row.reserve0 = itr->reserve0;
            row.reserve1 = itr->reserve1;
            row.liquidity = itr->liquidity;
            row.amplifier = itr->amplifier;
            row.virtual_price = itr->virtual_price;
            row.price0_last = itr->price0_last;
            row.price1_last = itr->price1_last;
            row.volume0 =itr->volume0;
            row.volume1 =itr->volume1;
            row.trades = itr->trades;
            row.last_updated = itr->last_updated;
        });
        ++itr;
    }
    sx::curve::setstatus_action setstatus( get_self(), { get_self(), "active"_n });
    setstatus.send( "copy"_n );
}